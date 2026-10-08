#include "graph/compiler/behavior_function_internal.h"
#include "graph/runtime/flow_internal.h"
#include "test/flow_snapshot.h"
#include "test/suite_internal.h"
#include "test/whole_name_fixture.h"

#include <dlfcn.h>
#include <time.h>

static behavior_function *entry(behavior_asset *a, graph *g)
{
    const behavior_graph_program *p = behavior_program_graph(behavior_program_publish(a), g);
    behavior_function *f = (behavior_function *)behavior_program_function(p, g->nodes[0]);
    if (!f) {
        const behavior_function_diagnostic
            *d = behavior_program_function_diagnostic(p, g->nodes[0]);
        fprintf(
            stderr,
            "name exclusion: %s graph=%s node=%llu pin=%d\n",
            d->reason,
            d->graph->name,
            (unsigned long long)d->node->id,
            d->pin
        );
    }
    assert(f);
    return f;
}
void emit_name_c(const char *path)
{
    FILE *out = fopen(path, "w");
    assert(out);
    for (int edge = 0; edge < 2; edge++) {
        for (int pure = 0; pure < 2; pure++) {
            for (int waits = 0; waits < 2; waits++) {
                behavior_asset *a = whole_name_fixture(edge, pure, waits);
                char name[64];
                snprintf(name, sizeof name, "generated_name_%d_%d_%d", edge, pure, waits);
                behavior_function_write_c(entry(a, a->graph), out, name);
                if (!waits) {
                    snprintf(name, sizeof name, "generated_name_return_%d_%d", edge, pure);
                    behavior_function_write_c(entry(a, a->graphs[1]), out, name);
                }
                behavior_asset_free(a);
            }
        }
    }
    assert(!fclose(out));
}
static void returned(bool edge, bool pure, void *library)
{
    for (int mode = 0; mode < (library ? 3 : 2); mode++) {
        for (int empty = 0; empty < 2; empty++) {
            behavior_asset *a = whole_name_fixture(edge, pure, false);
            const behavior_function *f = entry(a, a->graphs[1]);
            char text[] = "returned \"name\" \xE2\x98\x83";
            pin_value input = { .type = PIN_STR, .str = empty ? NULL : text },
                      out[2] = { 0 };
            out[pure ? 0 : 1].type = PIN_NAME;
            behavior_function_context ctx = { .arguments = &input,
                                              .argument_count = 1,
                                              .results = out,
                                              .budget = 100 };
            arena storage = { 0 };
            behavior_function_prepare_results(f, out, &storage);
            behavior_function_bind_storage(&ctx, &storage);
            behavior_function_activation activation = { 0 };
            behavior_function_activation_init(f, &activation);
            ctx.activation = &activation;
            uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
            int status;
            if (mode == 2) {
                char name[64];
                snprintf(name, sizeof name, "generated_name_return_%d_%d", edge, pure);
                int (*generated)(
                    behavior_function_context *
                ) = (int (*)(behavior_function_context *))dlsym(library, name);
                assert(generated);
                status = generated(&ctx);
            } else {
                status = behavior_function_run(f, &ctx, mode == 1);
            }
            assert(!status && eval == eval_oracle_calls() && flow == flow_oracle_calls());
            behavior_function_activation_free(&activation);
            behavior_asset_free(a);
            memset(text, 'x', sizeof text - 1);
            pin_value value = out[pure ? 0 : 1];
            assert(value.type == PIN_NAME && !strcmp(value.type_name, "name"));
            assert(!strcmp(value.str, empty ? "" : "returned \"name\" \xE2\x98\x83"));
            arena_free(&storage);
        }
    }
}
static void parity(bool edge, bool pure, bool waits, int scenario, int control, int budget, void *library)
{
    behavior_asset *a = whole_name_fixture(edge, pure, waits);
    graph *hold = a->graphs[2];
    assert(!graph_set_breakpoint(hold, hold->nodes[1], control == 3));
    behavior_function *f = entry(a, a->graph);
    void *code = f->artifact->code, *inspection = f->artifact->inspection_code,
         *dispatch = f->artifact->dispatch;
    test_flow_snapshot *expected = NULL;
    for (int mode = 0; mode < (library ? 4 : 3); mode++) {
        if (mode == 3) {
            char name[64];
            snprintf(name, sizeof name, "generated_name_%d_%d_%d", edge, pure, waits);
            f->artifact->code = dlsym(library, name);
            assert(f->artifact->code);
            f->artifact->inspection_code = f->artifact->code;
            f->artifact->dispatch = NULL;
        }
        behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
        assert(!behavior_instance_set_backend(
            bi,
            (
                behavior_backend[]
            ){ BEHAVIOR_ORACLE, BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT, BEHAVIOR_SLJIT }[mode]
        ));
        behavior_instance_set_trace_enabled(bi, control != 0);
        behavior_instance_set_breakpoints_enabled(bi, control == 3);
        char text[] = "retained \"name\" \xE2\x98\x83";
        pin_value input = { .type = PIN_STR,
                            .str = scenario == 1 ? NULL
                                 : scenario == 2 ? ""
                                                 : text };
        flow_options options = { .instance = bi, .step_budget = budget };
        flow_run *run = flow_run_start(
            a->graph,
            a->graph->nodes[0],
            (flow_inputs){ &input, 1 },
            &options,
            NULL
        );
        assert(run);
        memset(text, 'x', sizeof text - 1);
        uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
        int result = control == 1 ? flow_run_step(run) : flow_run_resume(run), at = 0;
        for (;;) {
            test_flow_snapshot snapshot = test_flow_snapshot_take(run, bi, result);
            if (!mode) {
                apush(expected, snapshot);
            } else {
                assert(run->whole_function_executed && at < acount(expected));
                test_flow_snapshot_same(snapshot, expected[at]);
                test_flow_snapshot_free(&snapshot);
            }
            assert(++at < 64);
            flow_state state = flow_run_status(run);
            if (state == FLOW_COMPLETED || state == FLOW_FAILED) {
                if (budget == 100) {
                    assert(state == FLOW_COMPLETED && bi->execution.variables[1].type == PIN_NAME);
                    assert(
                        !strcmp(bi->execution.variables[1].str, scenario ? "" : "retained \"name\" \xE2\x98\x83")
                    );
                }
                break;
            }
            if (state == FLOW_SUSPENDED) {
                pin_value replacement = { .type = PIN_STR, .str = "replacement" };
                assert(!behavior_instance_set_variable(bi, a->variables[0].id, &replacement));
                if (control == 2) {
                    assert(!flow_run_stop(run));
                    break;
                }
            }
            result = state == FLOW_SUSPENDED
                       ? flow_run_tick_at(run, flow_run_wake_time(run))
                       : flow_run_step(run);
        }
        if (mode) {
            assert(at == acount(expected));
            assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
        }
        flow_run_free(run);
        behavior_instance_destroy(bi);
    }
    for (int i = 0; i < acount(expected); i++) {
        test_flow_snapshot_free(&expected[i]);
    }
    afree(expected);
    f->artifact->code = code;
    f->artifact->inspection_code = inspection;
    f->artifact->dispatch = dispatch;
    behavior_asset_free(a);
}
void test_compiled_names(const char *path)
{
    void *library = path ? dlopen(path, RTLD_NOW | RTLD_LOCAL) : NULL;
    assert(!path || library);
    for (int edge = 0; edge < 2; edge++) {
        for (int pure = 0; pure < 2; pure++) {
            returned(edge, pure, library);
            for (int waits = 0; waits < 2; waits++) {
                for (int scenario = 0; scenario < 3; scenario++) {
                    parity(edge, pure, waits, scenario, 0, 100, library);
                    parity(edge, pure, waits, scenario, 1, 100, library);
                }
                for (int budget = 1; budget <= 4; budget++) {
                    parity(edge, pure, waits, 0, 0, budget, library);
                }
                parity(edge, pure, waits, 0, 2, 100, library);
                parity(edge, pure, waits, 0, 3, 100, library);
            }
        }
    }
    if (library) {
        assert(!dlclose(library));
    }
    puts("PASS: whole To Name, null/empty/UTF-8, explicit/edge conversions, pure/effectful calls, owned returns, waits, cancellation and inspection");
}
static double seconds()
{
    struct timespec t;
    assert(!clock_gettime(CLOCK_MONOTONIC, &t));
    return t.tv_sec + t.tv_nsec * 1e-9;
}
int bench_names(const char *path, int count, int rotation)
{
    assert(count > 0 && rotation >= 0);
    void *library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    assert(library);
    for (int pure = 0; pure < 2; pure++) {
        behavior_asset *a = whole_name_fixture(true, pure, false);
        const behavior_function *f = entry(a, a->graph);
        char name[64];
        snprintf(name, sizeof name, "generated_name_1_%d_0", pure);
        int (*generated)(
            behavior_function_context *
        ) = (int (*)(behavior_function_context *))dlsym(library, name);
        int (*native)(
            behavior_function_context *
        ) = (int (*)(behavior_function_context *))behavior_function_entry(f);
        assert(generated && native);
        for (int order = 0; order < 3; order++) {
            int mode = (order + rotation) % 3;
            behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
            pin_value input = { .type = PIN_STR, .str = "measured name" };
            behavior_function_context ctx = { .arguments = &input,
                                              .argument_count = 1,
                                              .budget = 100 };
            behavior_instance_bind_values(bi, &ctx);
            arena storage = { 0 };
            behavior_function_bind_storage(&ctx, &storage);
            behavior_function_activation activation = { 0 };
            behavior_function_activation_init(f, &activation);
            ctx.activation = &activation;
            behavior_function_prepare_host(f, &ctx);
            uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls(),
                     data = 0, vm = 0;
            double start = 0;
            for (int i = -64; i < count; i++) {
                if (!i) {
                    data = behavior_function_data_allocations();
                    vm = behavior_function_vm_allocations();
                    start = seconds();
                }
                arena_reset(&storage);
                ctx.steps = 0;
                int status = mode == 2 ? generated(&ctx)
                           : mode == 0 ? native(&ctx)
                                       : behavior_function_run(f, &ctx, false);
                assert(!status);
            }
            double elapsed = seconds() - start;
            assert(
                data == behavior_function_data_allocations() &&
                vm == behavior_function_vm_allocations()
            );
            assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
            assert(!strcmp(bi->execution.variables[1].str, input.str));
            printf(
                "%s names_%s ns=%.3f checksum=%zu\n",
                (const char *[]){ "SLJIT", "BYTECODE", "GENERATED_C" }[mode],
                pure ? "pure" : "effectful",
                elapsed * 1e9 / count,
                strlen(bi->execution.variables[1].str)
            );
            behavior_function_release_host(&ctx);
            behavior_function_activation_free(&activation);
            arena_free(&storage);
            behavior_instance_destroy(bi);
        }
        behavior_asset_free(a);
    }
    assert(!dlclose(library));
    return 0;
}
