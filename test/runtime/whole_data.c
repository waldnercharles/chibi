#include "graph/compiler/behavior_function_internal.h"
#include "graph/runtime/flow_internal.h"
#include "graph/runtime/runtime.h"
#include "graph/runtime/update.h"
#include "test/flow_snapshot.h"
#include "test/suite_internal.h"
#include "test/whole_data_fixture.h"
#include "test/whole_delay_fixture.h"

#include <dlfcn.h>

static void batch_failures()
{
    behavior_asset *a = whole_data_nested(false);
    for (int fail = -1; fail <= 2; fail++) {
        test_flow_snapshot expected = { 0 };
        for (int mode = 0; mode < 7; mode++) {
            behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
            assert(!behavior_instance_set_backend(
                bi,
                mode == 0  ? BEHAVIOR_ORACLE
                : mode % 2 ? BEHAVIOR_BYTECODE
                           : BEHAVIOR_SLJIT
            ));
            behavior_instance_set_trace_enabled(bi, false);
            whole_data_service service = { .bias = .5, .fail_at = fail };
            if (fail >= 0) {
                assert(!behavior_instance_bind_service(bi, "test.data", &service));
            }
            uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
            behavior_batch *batch = NULL;
            flow_run *run = NULL;
            if (mode >= 3) {
                if (mode >= 5) {
                    ecs_world *world = ecs_world_new();
                    ecs_entity entity = ecs_world_create_entity(world);
                    behavior_update_binding **bindings = NULL;
                    node *entry;
                    const behavior_function *
                        function = behavior_program_batch_event(a->program, "Run", &entry);
                    behavior_update_row row = {
                        .instance = bi,
                        .binding = behavior_update_prepare(&bindings, bi, function, entry),
                        .services_revision = bi->services_revision,
                        .entity = ecs_entity_to_u64(entity)
                    };
                    assert(row.binding);
                    void *code = mode == 6 ? behavior_update_compile() : NULL;
                    assert(mode != 6 || code);
                    int result;
                    behavior_update_row *end = code
                                                 ? ((behavior_update_fn)code)(
                                                       &row,
                                                       &row + 1,
                                                       (const ecs_execution_view *)world,
                                                       &result
                                                   )
                                                 : behavior_update_run_c(
                                                       &row,
                                                       &row + 1,
                                                       (const ecs_execution_view *)world,
                                                       &result,
                                                       NULL
                                                   );
                    assert(end == &row + (fail == 0));
                    if (fail) {
                        run = behavior_update_failure(&row, result);
                    }
                    behavior_update_free_code(code);
                    behavior_update_free_bindings(bindings);
                    ecs_world_free(world);
                } else {
                    assert(behavior_instance_dispatch_batch(bi, "Run", &batch, &run));
                }
                if (!fail) {
                    assert(!run && service.calls == 2);
                    assert(((whole_record *)bi->variables[0].ptr)->weight == 13.5);
                }
            } else {
                run = behavior_instance_dispatch(bi, "Run", (flow_inputs){ 0 }, NULL);
                assert(run);
            }
            if (run) {
                test_flow_snapshot
                    s = test_flow_snapshot_take(run, bi, flow_run_error(run)->code);
                if (!mode) {
                    expected = s;
                } else {
                    assert(run->whole_function_executed);
                    test_flow_snapshot_same(s, expected);
                    test_flow_snapshot_free(&s);
                }
            }
            assert(service.calls == (fail < 0 ? 0 : fail ? fail : 2));
            if (mode) {
                assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
            }
            behavior_batch_free(batch);
            behavior_instance_destroy(bi);
        }
        test_flow_snapshot_free(&expected);
    }
    behavior_asset_free(a);
}

static void services_and_generation()
{
    behavior_asset *a = whole_data_nested(false), *next = whole_data_nested(false);
    behavior_batch *batch = NULL;
    behavior_instance *instances[2];
    whole_data_service services[] = { { .bias = 1 }, { .bias = 2 } };
    for (int i = 0; i < 2; i++) {
        instances[i] = behavior_instance_create(a, NULL, NULL, NULL);
        behavior_instance_set_trace_enabled(instances[i], false);
        assert(!behavior_instance_bind_service(instances[i], "test.data", &services[i]));
    }
    for (int pass = 0; pass < 3; pass++) {
        if (pass == 2) {
            behavior_instance_update(instances[0], next);
        }
        for (int i = 0; i < 2; i++) {
            flow_run *failed = NULL;
            assert(
                behavior_instance_dispatch_batch(instances[i], "Run", &batch, &failed) && !failed
            );
            assert(
                ((whole_record *)instances[i]->variables[0].ptr)->weight ==
                12 + 3 * services[i].bias
            );
            assert(services[i].calls == 2 * (pass + 1));
        }
    }
    behavior_batch_free(batch);
    for (int i = 0; i < 2; i++) {
        behavior_instance_destroy(instances[i]);
    }
    behavior_asset_free(next);
    behavior_asset_free(a);
}

static void pure_calls()
{
    behavior_asset *source = whole_data_nested(false);
    const node_desc *desc = source->graphs[1]->nodes[2]->desc;
    behavior_asset *a = behavior_asset_new("Pure host data");
    graph *g = behavior_asset_add_function(a, "Read", true);
    node *query = graph_add_node(g, desc), *entry_node = g->nodes[0],
         *ret = g->nodes[1];
    for (int i = 0; i < 4; i++) {
        assert(graph_add_pin(g, entry_node, 1, &query->input_pins[i]) == i);
        assert(graph_add_pin(g, ret, 0, &query->output_pins[i]) == i);
        assert(!graph_connect_data(g, entry_node, i, query, i));
        assert(!graph_connect_data(g, query, i, ret, i));
    }
    node *call = behavior_asset_add_function_call_node(a, g);
    for (int i = 0; i < 4; i++) {
        pin_value value = source->graph->nodes[1]->input_pins[i + 1].default_value;
        assert(!graph_set_pin_default(a->graph, call, 0, i, &value));
    }
    behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
    behavior_instance_set_trace_enabled(bi, false);
    whole_data_service service = { .bias = .5 };
    assert(!behavior_instance_bind_service(bi, "test.data", &service));
    runtime_store *runtime = runtime_store_create(a->graph);
    arena storage = { 0 };
    for (int mode = 0; mode < 3; mode++) {
        behavior_backend backend = (behavior_backend[]){ BEHAVIOR_ORACLE,
                                                         BEHAVIOR_BYTECODE,
                                                         BEHAVIOR_SLJIT }[mode];
        assert(!behavior_instance_set_backend(bi, backend));
        runtime_store_reset_memo(runtime);
        service.calls = 0;
        eval_context ctx = { .instance = bi, .runtime = runtime, .services = bi->services };
        uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
        for (int pin = 0; pin < 4; pin++) {
            pin_value value;
            int result = !mode
                           ? graph_eval_output(a->graph, call, pin, &storage, &value, &ctx)
                           : behavior_program_output(
                                 behavior_program_graph(a->program, a->graph),
                                 call,
                                 pin,
                                 &storage,
                                 &value,
                                 &ctx,
                                 backend
                             );
            assert(!result);
            if (!pin) {
                assert(((whole_record *)value.ptr)->weight == 6.5);
            }
        }
        assert(service.calls == 1);
        if (mode) {
            assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
        }
    }
    arena_free(&storage);
    runtime_store_free(runtime);
    behavior_instance_destroy(bi);
    behavior_asset_free(a);
    behavior_asset_free(source);
}

static behavior_function *entry(behavior_asset *a)
{
    behavior_program *p = behavior_program_publish(a);
    behavior_function *f = (behavior_function *)
        behavior_program_function(behavior_program_graph(p, a->graph), a->graph->nodes[0]);
    assert(f && !behavior_program_diagnostic(p));
    return f;
}
static behavior_asset *fixture(bool waits)
{
    behavior_asset *a = whole_data_nested(true);
    if (waits) {
        whole_delay_add(a);
    }
    return a;
}
void emit_data_c(const char *path)
{
    FILE *out = fopen(path, "w");
    assert(out);
    for (int waits = 0; waits < 2; waits++) {
        behavior_asset *a = fixture(waits);
        behavior_function_write_c(entry(a), out, waits ? "generated_data_wait" : "generated_data");
        behavior_asset_free(a);
    }
    for (int kind = 0; kind < WHOLE_READ_KINDS; kind++) {
        behavior_asset *a = whole_read_branch_fixture(kind);
        char name[64];
        snprintf(name, sizeof name, "generated_read_branch%d", kind);
        behavior_function_write_c(entry(a), out, name);
        behavior_asset_free(a);
    }
    assert(!fclose(out));
}
static void parity(bool waits, int fail_at, unsigned budget, bool step, bool absent, void *library)
{
    behavior_asset *a = fixture(waits);
    behavior_function *f = entry(a);
    void *code = f->code, *inspection = f->inspection_code, *dispatch = f->dispatch;
    whole_record record = { .tag = 255,
                            .position = { -0.f, 1.e-35f },
                            .weight = -2.5,
                            .active = true,
                            .stamp = INT64_MIN + 42 };
    pin_value args[] = {
        { .type = PIN_STRUCT, .type_name = "WholeRecord", .ptr = absent ? NULL : &record },
        { .type = PIN_F64, .type_name = "double", .d = 1.25 },
        { .type = PIN_U32, .type_name = "unsigned int", .u = 0xffffffffu },
        { .type = PIN_HANDLE, .type_name = "ecs.entity", .u64 = UINT64_MAX - 7 }
    };
    test_flow_snapshot *expected = NULL;
    int *calls = NULL;
    for (int mode = 0; mode < (library ? 4 : 3); mode++) {
        if (mode == 3) {
            f->code = dlsym(library, waits ? "generated_data_wait" : "generated_data");
            assert(f->code);
            f->inspection_code = f->code;
            f->dispatch = NULL;
        }
        behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
        assert(!behavior_instance_set_backend(
            bi,
            (
                behavior_backend[]
            ){ BEHAVIOR_ORACLE, BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT, BEHAVIOR_SLJIT }[mode]
        ));
        behavior_instance_set_trace_enabled(bi, waits);
        whole_data_service service = { .bias = .375, .fail_at = fail_at };
        if (fail_at >= 0) {
            assert(!behavior_instance_bind_service(bi, "test.data", &service));
        }
        flow_options options = { .instance = bi, .step_budget = budget };
        flow_run *run = flow_run_start(a->graph, a->graph->nodes[0], (flow_inputs){ args, 4 }, &options, NULL);
        assert(run);
        uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
        int result = step ? flow_run_step(run) : flow_run_resume(run), at = 0;
        for (;;) {
            test_flow_snapshot s = test_flow_snapshot_take(run, bi, result);
            if (!mode) {
                apush(expected, s);
                apush(calls, service.calls);
            } else {
                assert(at < acount(expected) && run->whole_function_executed);
                test_flow_snapshot_same(s, expected[at]);
                assert(service.calls == calls[at]);
                test_flow_snapshot_free(&s);
            }
            assert(++at < 80);
            flow_state state = flow_run_status(run);
            if (state == FLOW_COMPLETED || state == FLOW_FAILED) {
                break;
            }
            if (state == FLOW_SUSPENDED) {
                service.bias += 10;
                result = flow_run_tick_at(run, flow_run_wake_time(run));
            } else {
                assert(state == FLOW_PAUSED);
                result = flow_run_step(run);
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
    }
    f->code = code;
    f->inspection_code = inspection;
    f->dispatch = dispatch;
    for (int i = 0; i < acount(expected); i++) {
        test_flow_snapshot_free(&expected[i]);
    }
    afree(expected);
    afree(calls);
    behavior_asset_free(a);
}
void test_compiled_data(const char *path)
{
    void *library = path ? dlopen(path, RTLD_NOW | RTLD_LOCAL) : NULL;
    assert(!path || library);
    batch_failures();
    services_and_generation();
    pure_calls();
    for (int waits = 0; waits < 2; waits++) {
        for (int fail = -1; fail <= 2; fail++) {
            for (unsigned budget = 1; budget <= 10; budget++) {
                for (int absent = 0; absent < 2; absent++) {
                    parity(waits, fail, budget, false, absent, library);
                    if (waits) {
                        parity(waits, fail, budget, true, absent, library);
                    }
                }
            }
        }
    }
    whole_data_smoke();
    for (int kind = 0; kind < WHOLE_READ_KINDS; kind++) {
        for (int mode = 0; mode < (library ? 2 : 1); mode++) {
            behavior_asset *a = whole_read_branch_fixture(kind);
            behavior_function *f = entry(a);
            void *code = f->code, *inspection = f->inspection_code,
                 *dispatch = f->dispatch;
            behavior_backend before = behavior_debug_backend(
                mode ? BEHAVIOR_SLJIT : BEHAVIOR_BYTECODE
            );
            if (mode) {
                char name[64];
                snprintf(name, sizeof name, "generated_read_branch%d", kind);
                f->code = dlsym(library, name);
                assert(f->code);
                f->inspection_code = f->code;
                f->dispatch = NULL;
            }
            whole_read_branch_check(a, kind, false);
            whole_read_branch_check(a, kind, true);
            behavior_debug_backend(before);
            f->code = code;
            f->inspection_code = inspection;
            f->dispatch = dispatch;
            behavior_asset_free(a);
        }
    }
    if (library) {
        assert(!dlclose(library));
    }
    test_note("PASS: nested host data, null structs, retained snapshots, errors, budgets, waits, stepping and watches across whole backends\n");
}
