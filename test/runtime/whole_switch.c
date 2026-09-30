#include "graph/compiler/behavior_function_internal.h"
#include "graph/model/registry.h"
#include "graph/runtime/eval.h"
#include "graph/runtime/flow_internal.h"
#include "graph/runtime/instance.h"
#include "test/flow_snapshot.h"
#include "test/support/report.h"
#include "test/whole_switch_fixture.h"

#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#if defined(__unix__) || defined(__APPLE__)
#include <sys/mman.h>
#include <unistd.h>
#endif

static void string_boundaries()
{
#if defined(__unix__) || defined(__APPLE__)
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    char *mapping = mmap(NULL, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    assert(mapping != MAP_FAILED);
    assert(!mprotect(mapping + page, page, PROT_NONE));
    const char *labels[] = { "a", "alpha", "12345678", "123456789", "雪" };
    for (int label = 0; label < (int)(sizeof labels / sizeof *labels); label++) {
        behavior_asset *asset = whole_switch_fixture(1);
        graph *g = whole_switch_root(asset);
        char name[64];
        snprintf(name, sizeof name, "Case:%s", labels[label]);
        assert(!graph_set_pin_name(g, g->nodes[2], true, 1, name));
        assert(!graph_set_pin_name(g, g->nodes[2], true, 2, name));
        behavior_function_diagnostic diagnostic;
        behavior_function *f = behavior_function_lower(asset, g, g->nodes[0], &diagnostic);
        assert(f && behavior_function_compile(f));
        int length = (int)strlen(labels[label]);
        for (int prefix = 0; prefix <= length + 1; prefix++) {
            char *text = mapping + page - prefix - 1;
            memcpy(text, labels[label], (size_t)(prefix > length ? length : prefix));
            if (prefix > length) {
                text[length] = '!';
            }
            text[prefix] = 0;
            for (int mode = 0; mode < 2; mode++) {
                behavior_function_activation activation = { 0 };
                behavior_function_activation_init(f, &activation);
                pin_value variables[] = { { .type = PIN_I32 }, { .type = PIN_I32 } };
                pin_value argument = { .type = PIN_STR, .str = text },
                          results[] = { { .type = PIN_FLOW }, { .type = PIN_I32 } };
                behavior_function_context context = { .activation = &activation,
                                                      .arguments = &argument,
                                                      .argument_count = 1,
                                                      .results = results,
                                                      .variables = variables,
                                                      .budget = 100 };
                arena storage = { 0 };
                behavior_function_bind_storage(&context, &storage);
                assert(!behavior_function_run(f, &context, mode == 0));
                assert(
                    results[1].i == (!prefix            ? 0
                                     : prefix == length ? 1
                                                        : 4)
                );
                assert(variables[1].i == 1);
                behavior_function_activation_free(&activation);
                arena_free(&storage);
            }
        }
        behavior_function_free(f);
        behavior_asset_free(asset);
    }
    assert(!munmap(mapping, page * 2));
#endif
}

static void check(int kind, int sample, int control, int budget, void *library)
{
    behavior_asset *asset = whole_switch_fixture(kind);
    graph *g = whole_switch_root(asset);
    if (control == 4) {
        graph *body = asset->graphs[1];
        assert(!graph_set_breakpoint(body, body->nodes[2], true));
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
        behavior_instance_set_trace_enabled(bi, control != 0);
        behavior_instance_set_breakpoints_enabled(bi, control == 4);
        behavior_function *f = (behavior_function *)
            behavior_program_function(behavior_program_graph(asset->program, g), g->nodes[0]);
        if (mode && !f) {
            behavior_function_diagnostic diagnostic;
            behavior_function *lowered = behavior_function_lower(asset, g, g->nodes[0], &diagnostic);
            fprintf(
                stderr,
                "switch kind=%d: %s\n",
                kind,
                diagnostic.reason ? diagnostic.reason : "publication omitted compiled body"
            );
            behavior_function_free(lowered);
            assert(f);
        }
        void *code = f ? f->code : NULL, *inspection = f ? f->inspection_code : NULL,
             *dispatch = f ? f->dispatch : NULL;
        if (mode == 3) {
            char name[64];
            snprintf(name, sizeof name, "whole_switch_%d", kind);
            f->code = f->inspection_code = dlsym(library, name);
            f->dispatch = NULL;
            assert(f->code);
        }
        pin_value argument = whole_switch_argument(kind, sample);
        flow_options options = { .instance = bi, .step_budget = (unsigned)budget };
        flow_run *run = flow_run_start(g, g->nodes[0], (flow_inputs){ &argument, 1 }, &options, NULL);
        uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
        int result = control == 2 ? flow_run_step(run) : flow_run_resume(run);
        int at = 0, breakpoint_stops = 0;
        for (;;) {
            if (mode) {
                assert(run->whole_function_executed && !acount(run->children));
            }
            test_flow_snapshot snapshot = test_flow_snapshot_take(run, bi, result);
            if (!mode) {
                apush(expected, snapshot);
            } else {
                assert(at < acount(expected));
                if (snapshot.steps != expected[at].steps) {
                    fprintf(
                        stderr,
                        "switch kind=%d sample=%d control=%d budget=%d mode=%d "
                        "stop=%d node=%llu/%llu steps=%lu/%lu\n",
                        kind,
                        sample,
                        control,
                        budget,
                        mode,
                        at,
                        (unsigned long long)snapshot.node,
                        (unsigned long long)expected[at].node,
                        snapshot.steps,
                        expected[at].steps
                    );
                }
                test_flow_snapshot_same(snapshot, expected[at]);
                test_flow_snapshot_free(&snapshot);
            }
            assert(++at < 100);
            flow_state state = flow_run_status(run);
            breakpoint_stops += state == FLOW_PAUSED && run->breakpoint_node != NULL;
            if (state == FLOW_COMPLETED || state == FLOW_FAILED) {
                break;
            }
            if (state == FLOW_SUSPENDED) {
                if (control == 3) {
                    assert(!flow_run_pause(run));
                }
                result = flow_run_tick_at(run, flow_run_wake_time(run));
            } else {
                assert(state == FLOW_PAUSED);
                result = control == 3 ? flow_run_step_over(run) : flow_run_step(run);
            }
        }
        if (!result) {
            assert(bi->variables[0].i == whole_switch_expected(kind, sample));
            assert(bi->variables[1].i == (kind & WHOLE_SWITCH_LOOP ? 3 : 1));
            if (control == 4 && !(kind & WHOLE_SWITCH_CALL)) {
                assert(breakpoint_stops == (kind & WHOLE_SWITCH_LOOP ? 3 : 1));
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
        if (mode == 3) {
            f->code = code;
            f->inspection_code = inspection;
            f->dispatch = dispatch;
        }
    }
    for (int i = 0; i < acount(expected); i++) {
        test_flow_snapshot_free(&expected[i]);
    }
    afree(expected);
    behavior_asset_free(asset);
}
void test_compiled_switches(const char *path)
{
    string_boundaries();
    void *library = path ? dlopen(path, RTLD_NOW) : NULL;
    assert(!path || library);
    for (int kind = 0; kind < WHOLE_SWITCH_COUNT; kind++) {
        for (int sample = 0; sample < 8; sample++) {
            for (int control = 0; control < 5; control++) {
                check(kind, sample, control, 100, library);
            }
        }
        for (int budget = 1; budget < 12; budget++) {
            check(kind, 2, 1, budget, library);
        }
    }
    if (library) {
        dlclose(library);
    }
    test_note(
        "PASS: whole switch oracle/native/bytecode%s parity for cases, state, "
        "loops, waits, debugger snapshots and budgets; zero compiled oracle "
        "calls\n",
        path ? "/generated-C" : ""
    );
}
void emit_switch_c(const char *path)
{
    FILE *out = fopen(path, "w");
    assert(out);
    for (int kind = 0; kind < WHOLE_SWITCH_COUNT; kind++) {
        behavior_asset *asset = whole_switch_fixture(kind);
        graph *g = whole_switch_root(asset);
        behavior_function_diagnostic diagnostic;
        behavior_function *f = behavior_function_lower(asset, g, g->nodes[0], &diagnostic);
        assert(f);
        char name[64];
        snprintf(name, sizeof name, "whole_switch_%d", kind);
        behavior_function_write_c(f, out, name);
        behavior_function_free(f);
        behavior_asset_free(asset);
    }
    assert(!fclose(out));
}

static double seconds()
{
    struct timespec now;
    assert(!clock_gettime(CLOCK_MONOTONIC, &now));
    return now.tv_sec + now.tv_nsec * 1e-9;
}
int bench_switch(const char *path, int count, int rotation)
{
    assert(count >= 1000);
    void *library = dlopen(path, RTLD_NOW);
    assert(library);
    uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
    const int kinds[] = { 0, 1, 2, 3, 8, 9, 10, 11 };
    for (int k = 0; k < (int)(sizeof kinds / sizeof *kinds); k++) {
        int kind = kinds[k];
        behavior_asset *asset = whole_switch_fixture(kind);
        graph *g = whole_switch_root(asset);
        behavior_function_diagnostic diagnostic;
        behavior_function *f = behavior_function_lower(asset, g, g->nodes[0], &diagnostic);
        assert(f && behavior_function_compile(f));
        char name[64];
        snprintf(name, sizeof name, "whole_switch_%d", kind);
        typedef int (*entry_fn)(behavior_function_context *);
        entry_fn entries[] = { (entry_fn)behavior_function_entry(f),
                               (entry_fn)dlsym(library, name) };
        assert(entries[0] && entries[1]);
        pin_value arguments[8];
        uint64_t expected = 0;
        for (int i = 0; i < 8; i++) {
            arguments[i] = whole_switch_argument(kind, i);
            expected += (uint64_t)(whole_switch_expected(kind, i) +
                                   (kind & WHOLE_SWITCH_LOOP ? 3 : 1)) *
                        (unsigned)(count / 8 + (i < count % 8));
        }
        for (int order = 0; order < 3; order++) {
            int mode = (order + rotation) % 3;
            behavior_function_activation activation = { 0 };
            behavior_function_activation_init(f, &activation);
            pin_value variables[] = { { .type = PIN_I32 }, { .type = PIN_I32 } };
            pin_value argument = { 0 },
                      results[] = { { .type = PIN_FLOW }, { .type = PIN_I32 } };
            behavior_function_context context = { .activation = &activation,
                                                  .arguments = &argument,
                                                  .argument_count = 1,
                                                  .results = results,
                                                  .variables = variables,
                                                  .budget = 100 };
            arena storage = { 0 };
            behavior_function_bind_storage(&context, &storage);
            behavior_function_prepare_host(f, &context);
            context.owned_host = true;
            uint64_t checksum = 0;
            double start = 0;
            for (int pass = 0; pass < 2; pass++) {
                int iterations = pass ? count : 1024;
                checksum = 0;
                start = seconds();
                for (int i = 0; i < iterations; i++) {
                    argument = arguments[i & 7];
                    variables[0].i = variables[1].i = 0;
                    activation.pc = 0;
                    activation.steps = context.steps = 0;
                    context.returned_node_id = 0;
                    int result = mode < 2
                                   ? entries[mode](&context)
                                   : behavior_function_run(f, &context, false);
                    assert(!result);
                    checksum += (uint64_t)(results[1].i + variables[1].i);
                }
            }
            double elapsed = seconds() - start;
            assert(checksum == expected);
            printf(
                "%s switch_%d ns=%.3f checksum=%llu\n",
                (const char *[]){ "SLJIT", "GENERATED_C", "BYTECODE" }[mode],
                kind,
                elapsed * 1e9 / count,
                (unsigned long long)checksum
            );
            behavior_function_release_host(&context);
            behavior_function_activation_free(&activation);
            arena_free(&storage);
        }
        behavior_function_free(f);
        behavior_asset_free(asset);
    }
    assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
    dlclose(library);
    return 0;
}
