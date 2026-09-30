#include "graph/compiler/behavior_function_internal.h"
#include "graph/document/serialize.h"
#include "graph/model/registry.h"
#include "graph/nodes/state_nodes.h"
#include "graph/runtime/flow_internal.h"
#include "graph/runtime/instance.h"
#include "test/flow_snapshot.h"
#include "test/support/report.h"
#include "test/whole_state_fixture.h"

#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static void check(int kind, int control, void *library)
{
    behavior_asset *asset = whole_state_fixture(kind, true);
    graph *g = whole_state_root(asset);
    node *state = asset->graphs[1]->nodes[2];
    if (control == 3) {
        assert(!graph_set_breakpoint(asset->graphs[1], state, true));
    }
    test_flow_snapshot *expected = NULL;
    pin_value states[11];
    int calls[11][2];
    for (int mode = 0; mode < (library ? 4 : 3); mode++) {
        behavior_instance *bi = behavior_instance_create(asset, NULL, NULL, NULL);
        assert(!behavior_instance_set_backend(
            bi,
            (
                behavior_backend[]
            ){ BEHAVIOR_ORACLE, BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT, BEHAVIOR_SLJIT }[mode]
        ));
        behavior_instance_set_trace_enabled(bi, control != 0);
        behavior_instance_set_breakpoints_enabled(bi, control == 3);
        whole_state_service service = { 0 };
        if (control == 4) {
            service.fail_at[0] = 1;
            service.fail_at[1] = 2;
        }
        assert(!behavior_instance_bind_service(bi, "test.state", &service));
        behavior_function *f = (behavior_function *)
            behavior_program_function(behavior_program_graph(asset->program, g), g->nodes[0]);
        if (!f) {
            const behavior_function_diagnostic *d = behavior_program_function_diagnostic(
                behavior_program_graph(asset->program, g),
                g->nodes[0]
            );
            fprintf(stderr, "state kind=%d exclusion=%s\n", kind, d ? d->reason : "missing");
        }
        assert(f);
        void *code = f->code, *inspection = f->inspection_code, *dispatch = f->dispatch;
        if (mode == 3) {
            char name[64];
            snprintf(name, sizeof name, "whole_state_%d", kind);
            f->code = f->inspection_code = dlsym(library, name);
            f->dispatch = NULL;
            assert(f->code);
        }
        int at = 0;
        const int actions[] = { 0, 0, 1, 0, 2, 0, 3, 0, 1, 0, -1 };
        for (int round = 0; round < 11; round++) {
            pin_value args[] = { { .type = PIN_I32,
                                   .i = control == 5 && !round ? 1 : actions[round] },
                                 { .type = PIN_BOOL, .b = round % 3 == 0 },
                                 { .type = PIN_I32, .i = round % 5 - 1 } };
            flow_options options = { .instance = bi, .step_budget = 100 };
            flow_run *run = flow_run_start(g, g->nodes[0], (flow_inputs){ args, 3 }, &options, NULL);
            uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
            int result = control == 2 ? flow_run_step(run) : flow_run_resume(run);
            int stops = 0;
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
                at++;
                assert(++stops < 100);
                flow_state status = flow_run_status(run);
                if (status == FLOW_COMPLETED || status == FLOW_FAILED) {
                    break;
                }
                if (status == FLOW_SUSPENDED) {
                    result = flow_run_tick_at(run, flow_run_wake_time(run));
                } else {
                    assert(status == FLOW_PAUSED);
                    result = flow_run_step(run);
                }
            }
            if (mode) {
                assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
                assert(bi->node_states[0].type == states[round].type);
                assert(bi->node_states[0].u64 == states[round].u64);
                if (service.calls[0] != calls[round][0] ||
                    service.calls[1] != calls[round][1]) {
                    fprintf(
                        stderr,
                        "calls=%d,%d expected=%d,%d\n",
                        service.calls[0],
                        service.calls[1],
                        calls[round][0],
                        calls[round][1]
                    );
                    assert(false);
                }
            } else {
                states[round] = bi->node_states[0];
                calls[round][0] = service.calls[0];
                calls[round][1] = service.calls[1];
            }
            flow_run_free(run);
        }
        assert(!mode || at == acount(expected));
        if ((kind & 3) == 0 || (kind & 3) == 3) {
            assert(
                service.calls[0] == (control == 4                      ? 2
                                     : control == 5 && (kind & 3) == 0 ? 0
                                                                       : 1)
            );
        }
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

static void generations()
{
    for (int type = 0; type < 4; type++) {
        behavior_asset *a = whole_state_fixture(type, false);
        behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
        node *n = a->graphs[1]->nodes[2];
        pin_value expected = { .type = type == 1 ? PIN_I32 : PIN_BOOL };
        if (type == 1) {
            expected.i = 17;
        } else {
            expected.b = true;
        }
        *behavior_instance_scratch(bi, n->id) = expected;
        char *json = behavior_asset_save_json(a);
        behavior_asset *next = behavior_asset_load_json(json);
        free(json);
        assert(next);
        node *extra = graph_add_node(next->graphs[1], &NODE_Gate);
        /* Insert an uninitialized slot before the live state to change its index. */
        graph *g = next->graphs[1];
        g->nodes[acount(g->nodes) - 1] = g->nodes[2];
        g->nodes[2] = extra;
        assert(behavior_instance_can_update(bi, next) == INSTANCE_UPDATE_READY);
        uint64_t id = n->id;
        behavior_instance_update(bi, next);
        behavior_asset_free(a);
        assert(acount(bi->node_states) == 2 && !bi->node_states[0].type);
        assert(behavior_instance_scratch(bi, id) == &bi->node_states[1]);
        assert(
            bi->node_states[1].type == expected.type &&
            bi->node_states[1].u64 == expected.u64
        );
        for (int i = 0; i < 100; i++) {
            behavior_instance_scratch(bi, UINT64_C(1000000) + (unsigned)i)->i = i;
        }
        assert(bi->node_states[1].u64 == expected.u64);
        flow_run *run = behavior_instance_dispatch(bi, "Tick", (flow_inputs){ 0 }, NULL);
        assert(run && flow_run_status(run) == FLOW_COMPLETED && run->whole_function_executed);
        assert(bi->variables[0].i == (type < 2 ? 0 : 1));
        flow_run_free(run);
        assert(!behavior_instance_pool_release(bi));
        assert(behavior_instance_pool_reuse(bi, next, NULL, NULL, NULL) == bi);
        assert(
            acount(bi->node_states) == 2 && !bi->node_states[0].type &&
            !bi->node_states[1].type
        );
        behavior_instance_destroy(bi);
        behavior_asset_free(next);
    }
}
static void missing_instance()
{
    behavior_asset *a = whole_state_fixture(0, false);
    graph *g = whole_state_root(a);
    behavior_function_diagnostic d;
    behavior_function *f = behavior_function_lower(a, g, g->nodes[0], &d);
    assert(f && behavior_function_compile(f));
    for (int mode = 0; mode < 2; mode++) {
        behavior_function_activation activation = { 0 };
        behavior_function_activation_init(f, &activation);
        pin_value args[] = { { .type = PIN_I32 }, { .type = PIN_BOOL }, { .type = PIN_I32 } };
        behavior_function_context ctx = { .activation = &activation,
                                          .arguments = args,
                                          .argument_count = 3,
                                          .budget = 100 };
        arena storage = { 0 };
        behavior_function_bind_storage(&ctx, &storage);
        assert(behavior_function_run(f, &ctx, mode == 1) == FLOW_ERR_STATE);
        assert(ctx.node_id == g->nodes[2]->id && ctx.error_pin == -1);
        behavior_function_activation_free(&activation);
        arena_free(&storage);
    }
    behavior_function_free(f);
    behavior_asset_free(a);
}
void test_compiled_states(const char *path)
{
    whole_state_smoke();
    generations();
    missing_instance();
    void *library = path ? dlopen(path, RTLD_NOW) : NULL;
    assert(!path || library);
    for (int kind = 0; kind < WHOLE_STATE_COUNT; kind++) {
        for (int control = 0; control < 6; control++) {
            check(kind, control, library);
        }
    }
    if (library) {
        dlclose(library);
    }
    test_note(
        "PASS: whole state oracle/bytecode/SLJIT%s parity for lazy inputs, "
        "resets, loops, waits, debugger and hot apply\n",
        path ? "/generated-C" : ""
    );
}
void emit_state_c(const char *path)
{
    FILE *out = fopen(path, "w");
    assert(out);
    for (int kind = 0; kind < WHOLE_STATE_COUNT; kind++) {
        for (int effects = 0; effects < 2; effects++) {
            behavior_asset *a = whole_state_fixture(kind, effects != 0);
            graph *g = whole_state_root(a);
            behavior_function_diagnostic d;
            behavior_function *f = behavior_function_lower(a, g, g->nodes[0], &d);
            assert(f);
            char name[64];
            snprintf(name, sizeof name, "whole_state_%d%s", kind, effects ? "" : "_plain");
            behavior_function_write_c(f, out, name);
            behavior_function_free(f);
            behavior_asset_free(a);
        }
    }
    assert(!fclose(out));
}

static double seconds()
{
    struct timespec now;
    assert(!clock_gettime(CLOCK_MONOTONIC, &now));
    return now.tv_sec + now.tv_nsec * 1e-9;
}
static uint64_t expected_checksum(int kind, int count)
{
    int total = 0, counter = 0;
    bool initialized = false, flag = false;
    uint64_t checksum = 0;
    const int actions[] = { 0, 0, 1, 0, 2, 0, 3, 0 };
    for (int i = 0; i < count; i++) {
        int action = actions[i % 8];
        for (int repeat = 0; repeat < (kind & WHOLE_STATE_LOOP ? 3 : 1); repeat++) {
            switch (kind & 3) {
                case 0:
                    if (action == 1) {
                        flag = false;
                    }
                    if (!action && !flag) {
                        total++;
                        flag = true;
                    }
                    break;
                case 1:
                    if (action == 1) {
                        counter = 0;
                    }
                    if (!action && counter < 3) {
                        counter++;
                        total++;
                    }
                    break;
                case 2: total += !action; break;
                case 3:
                    if (!initialized) {
                        flag = true;
                        initialized = true;
                    }
                    if (!action) {
                        total += flag;
                    }
                    if (action == 1) {
                        flag = true;
                    }
                    if (action == 2) {
                        flag = false;
                    }
                    if (action == 3) {
                        flag = !flag;
                    }
                    break;
            }
        }
        checksum += (unsigned)total;
    }
    return checksum;
}
int bench_state(const char *path, int count, int rotation)
{
    assert(count >= 1000 && rotation >= 0);
    void *library = dlopen(path, RTLD_NOW);
    assert(library);
    uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
    const int kinds[] = { 0, 1, 2, 3, 8, 9, 10, 11 };
    for (int k = 0; k < 8; k++) {
        int kind = kinds[k];
        uint64_t expected = expected_checksum(kind, count);
        behavior_asset *a = whole_state_fixture(kind, false);
        graph *g = whole_state_root(a);
        behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
        const behavior_function *f = behavior_program_function(
            behavior_program_graph(a->program, g),
            g->nodes[0]
        );
        assert(f);
        char name[64];
        snprintf(name, sizeof name, "whole_state_%d_plain", kind);
        typedef int (*entry_fn)(behavior_function_context *);
        entry_fn entries[] = { (entry_fn)behavior_function_entry(f),
                               (entry_fn)dlsym(library, name) };
        assert(entries[0] && entries[1]);
        for (int order = 0; order < 3; order++) {
            int mode = (order + rotation) % 3;
            behavior_function_activation activation = { 0 };
            behavior_function_activation_init(f, &activation);
            pin_value args[] = { { .type = PIN_I32 },
                                 { .type = PIN_BOOL },
                                 { .type = PIN_I32, .i = 3 } },
                      result = { 0 };
            behavior_function_context ctx = { .activation = &activation,
                                              .arguments = args,
                                              .argument_count = 3,
                                              .results = &result,
                                              .budget = 100 };
            arena storage = { 0 };
            behavior_function_bind_storage(&ctx, &storage);
            behavior_instance_bind_values(bi, &ctx);
            behavior_function_prepare_host(f, &ctx);
            ctx.owned_host = true;
            uint64_t checksum = 0;
            double start = 0;
            const int actions[] = { 0, 0, 1, 0, 2, 0, 3, 0 };
            for (int pass = 0; pass < 2; pass++) {
                bi->node_states[0] = (pin_value){ 0 };
                bi->variables[0].i = 0;
                checksum = 0;
                start = seconds();
                for (int i = 0; i < (pass ? count : 1024); i++) {
                    args[0].i = actions[i % 8];
                    activation.pc = 0;
                    activation.steps = ctx.steps = 0;
                    ctx.returned_node_id = 0;
                    int status = mode < 2 ? entries[mode](&ctx)
                                          : behavior_function_run(f, &ctx, false);
                    assert(!status);
                    checksum += (unsigned)bi->variables[0].i;
                }
            }
            double elapsed = seconds() - start;
            assert(checksum == expected);
            printf(
                "%s state_%d ns=%.3f checksum=%llu\n",
                (const char *[]){ "SLJIT", "GENERATED_C", "BYTECODE" }[mode],
                kind,
                elapsed * 1e9 / count,
                (unsigned long long)checksum
            );
            behavior_function_release_host(&ctx);
            behavior_function_activation_free(&activation);
            arena_free(&storage);
        }
        behavior_instance_destroy(bi);
        behavior_asset_free(a);
    }
    assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
    dlclose(library);
    return 0;
}
