#include "graph/compiler/behavior_function_internal.h"
#include "graph/document/serialize.h"
#include "graph/model/registry.h"
#include "graph/nodes/state_nodes.h"
#include "graph/runtime/flow_internal.h"
#include "test/flow_snapshot.h"
#include "test/support/report.h"
#include "test/whole_multigate_fixture.h"

#include <assert.h>
#include <dlfcn.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const int output_counts[] = { 0, 1, 2, 5, 17 };
static behavior_function *function(behavior_asset *a)
{
    graph *g = whole_multigate_root(a);
    const behavior_graph_program *p = behavior_program_graph(behavior_program_publish(a), g);
    behavior_function *f = (behavior_function *)behavior_program_function(p, g->nodes[0]);
    if (!f) {
        const behavior_function_diagnostic
            *d = behavior_program_function_diagnostic(p, g->nodes[0]);
        fprintf(stderr, "MultiGate exclusion: %s\n", d ? d->reason : "missing");
    }
    assert(f);
    return f;
}
static void symbol(char *name, size_t size, int outputs, int kind, bool effects)
{
    snprintf(name, size, "whole_multigate_%d_%d_%d", outputs, kind, effects);
}
static void check(int outputs, int kind, int control, void *library)
{
    behavior_asset *a = whole_multigate_fixture(outputs, kind, true);
    graph *g = whole_multigate_root(a);
    assert(!graph_set_breakpoint(a->graphs[1], a->graphs[1]->nodes[2], control == 3));
    behavior_function *f = function(a);
    void *code = f->code, *inspection = f->inspection_code, *dispatch = f->dispatch;
    test_flow_snapshot *expected = NULL;
    enum
    {
        ROUNDS = 32
    };
    char *masks[ROUNDS] = { 0 };
    int draws[ROUNDS], counts[ROUNDS];
    whole_multigate_service expected_service = { 0 };
    for (int mode = 0; mode < (library ? 4 : 3); mode++) {
        if (mode == 3) {
            char name[64];
            symbol(name, sizeof name, outputs, kind, true);
            f->code = f->inspection_code = dlsym(library, name);
            f->dispatch = NULL;
            assert(f->code);
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
        whole_multigate_service service = { .fail_at = control >= 5 && control <= 7
                                                         ? control - 4
                                                         : 0 };
        assert(!behavior_instance_bind_service(bi, "test.multigate", &service));
        srand(5791);
        int at = 0;
        for (int round = 0; round < ROUNDS; round++) {
            pin_value args[] = { { .type = PIN_BOOL, .b = round == 0 || round == 25 },
                                 { .type = PIN_BOOL, .b = round >= 20 },
                                 { .type = PIN_BOOL, .b = round % 3 != 0 },
                                 { .type = PIN_I32,
                                   .i = round == 3 ? INT_MIN
                                      : round == 4 ? INT_MAX
                                                   : round % 7 - 2 } };
            flow_options options = { .instance = bi,
                                     .step_budget = control == 8 ? 5 : 100 };
            flow_run *run = flow_run_start(g, g->nodes[0], (flow_inputs){ args, 4 }, &options, NULL);
            assert(run);
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
                assert(++stops < 200);
                flow_state status = flow_run_status(run);
                if (status == FLOW_COMPLETED || status == FLOW_FAILED) {
                    break;
                }
                if (control == 4 && status == FLOW_SUSPENDED) {
                    assert(!flow_run_stop(run));
                    break;
                }
                result = status == FLOW_SUSPENDED
                           ? flow_run_tick_at(run, flow_run_wake_time(run))
                           : flow_run_step(run);
            }
            int draw = rand();
            const pin_value *state = &bi->node_states[0];
            if (!mode) {
                masks[round] = state->type == PIN_STR ? strdup(state->str) : NULL;
                draws[round] = draw;
                counts[round] = service.count;
            } else {
                assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
                if ((state->type == PIN_STR) != (masks[round] != NULL)) {
                    fprintf(
                        stderr,
                        "MultiGate mismatch outputs=%d kind=%d control=%d "
                        "mode=%d round=%d type=%d expected=%s\n",
                        outputs,
                        kind,
                        control,
                        mode,
                        round,
                        state->type,
                        masks[round] ? masks[round] : "uninitialized"
                    );
                    assert(false);
                }
                assert(!masks[round] || !strcmp(state->str, masks[round]));
                assert(draw == draws[round] && service.count == counts[round]);
                assert(!memcmp(
                    service.calls,
                    expected_service.calls,
                    (size_t)service.count * sizeof *service.calls
                ));
            }
            if (!round) {
                assert(!service.count && state->type == PIN_FLOW);
            }
            flow_run_free(run);
        }
        if (!mode) {
            expected_service = service;
        }
        assert(!mode || at == acount(expected));
        /* Every completed input group preserves the declared evaluation order. */
        if (!control) {
            assert(service.count % 3 == 0);
            for (int i = 0; i < service.count; i++) {
                assert(service.calls[i] == 4 - i % 3);
            }
        }
        behavior_instance_destroy(bi);
    }
    for (int i = 0; i < acount(expected); i++) {
        test_flow_snapshot_free(&expected[i]);
    }
    afree(expected);
    for (int i = 0; i < ROUNDS; i++) {
        free(masks[i]);
    }
    f->code = code;
    f->inspection_code = inspection;
    f->dispatch = dispatch;
    behavior_asset_free(a);
}
static void ownership()
{
    behavior_asset *a = whole_multigate_fixture(2, 0, false);
    graph_add_node(a->graphs[1], &NODE_MultiGate);
    behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
    behavior_function_context ctx = { 0 };
    behavior_instance_bind_values(bi, &ctx);
    for (int i = 0; i < 2; i++) {
        assert(ctx.select_state(bi, &(behavior_state_selection){ i, 2 }, 0, 0) == 0);
        assert(!strcmp(bi->node_states[i].str, "10"));
    }
    /* Interned names must not borrow a mutable selection mask. */
    const char *name = ctx.own_type_name(bi, "10");
    assert(name != bi->node_states[0].str && name != bi->node_states[1].str);
    char *json = behavior_asset_save_json(a);
    behavior_asset *next = behavior_asset_load_json(json);
    free(json);
    assert(next);
    graph *g = next->graphs[1];
    int last = acount(g->nodes) - 1;
    node *first = g->nodes[2];
    g->nodes[2] = g->nodes[last];
    g->nodes[last] = first;
    assert(behavior_instance_can_update(bi, next) == INSTANCE_UPDATE_READY);
    behavior_instance_update(bi, next);
    behavior_asset_free(a);
    assert(bi->node_states[0].str != bi->node_states[1].str);
    assert(ctx.select_state(bi, &(behavior_state_selection){ 0, 2 }, 0, BEHAVIOR_STATE_RESET) == -1);
    assert(!strcmp(bi->node_states[0].str, "00"));
    assert(!strcmp(bi->node_states[1].str, "10") && !strcmp(name, "10"));
    flow_run *run = behavior_instance_dispatch(bi, "Tick", (flow_inputs){ 0 }, NULL);
    assert(run && run->whole_function_executed && flow_run_status(run) == FLOW_COMPLETED);
    assert(bi->variables[0].i == 1 && !strcmp(bi->node_states[1].str, "11"));
    flow_run_free(run);
    json = behavior_asset_save_json(next);
    behavior_asset *changed = behavior_asset_load_json(json);
    free(json);
    pin_spec extra = { .name = "Out2", .type = PIN_FLOW, .kind = PIN_EXEC };
    assert(graph_add_pin(changed->graphs[1], changed->graphs[1]->nodes[2], true, &extra) == 2);
    assert(behavior_instance_can_update(bi, changed) == INSTANCE_UPDATE_RESTART);
    behavior_asset_free(changed);
    assert(!behavior_instance_pool_release(bi));
    assert(behavior_instance_pool_reuse(bi, next, NULL, NULL, NULL) == bi);
    assert(!bi->node_states[0].type && !bi->node_states[1].type);
    for (int i = 0; i < acount(bi->strings); i++) {
        assert(strcmp(bi->strings[i], "10") && strcmp(bi->strings[i], "00"));
    }
    behavior_instance_destroy(bi);
    behavior_asset_free(next);
}
static void boundaries()
{
    behavior_asset *a = whole_multigate_fixture(5, 0, false);
    behavior_function *f = function(a);
    for (int mode = 0; mode < 2; mode++) {
        pin_value args[] = { { .type = PIN_BOOL },
                             { .type = PIN_BOOL },
                             { .type = PIN_BOOL, .b = true },
                             { .type = PIN_I32, .i = 2 } };
        behavior_function_activation activation = { 0 };
        behavior_function_activation_init(f, &activation);
        behavior_function_context ctx = { .activation = &activation,
                                          .arguments = args,
                                          .argument_count = 4,
                                          .budget = 100 };
        assert(behavior_function_run(f, &ctx, mode == 1) == FLOW_ERR_STATE);
        assert(ctx.node_id == a->graphs[1]->nodes[2]->id && ctx.error_pin == -1);
        behavior_function_activation_free(&activation);
        behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
        assert(!behavior_instance_set_backend(bi, mode ? BEHAVIOR_SLJIT : BEHAVIOR_BYTECODE));
        behavior_instance_set_trace_enabled(bi, false);
        srand(9247);
        (void)rand();
        int next_draw = rand();
        srand(9247);
        graph *g = whole_multigate_root(a);
        flow_options options = { .instance = bi };
        flow_run *run = flow_run_start(g, g->nodes[0], (flow_inputs){ args, 4 }, &options, NULL);
        assert(run && !flow_run_resume(run) && run->whole_function_executed);
        assert(bi->variables[0].i == 2 && rand() == next_draw);
        flow_run_free(run);
        behavior_instance_destroy(bi);
    }
    behavior_asset_free(a);
}
void test_compiled_multigate(const char *path)
{
    whole_multigate_smoke();
    void *library = path ? dlopen(path, RTLD_NOW | RTLD_LOCAL) : NULL;
    assert(!path || library);
    for (size_t i = 0; i < sizeof output_counts / sizeof *output_counts; i++) {
        for (int kind = 0; kind < WHOLE_MULTIGATE_COUNT; kind++) {
            for (int control = 0; control < 9; control++) {
                check(output_counts[i], kind, control, library);
            }
        }
    }
    ownership();
    boundaries();
    if (library) {
        assert(!dlclose(library));
    }
    test_note(
        "PASS: MultiGate oracle/bytecode/SLJIT%s parity, random draws, reset, "
        "input effects, loops, waits, cancellation, source inspection and "
        "state ownership\n",
        path ? "/generated-C" : ""
    );
}
void emit_multigate_c(const char *path)
{
    FILE *out = fopen(path, "w");
    assert(out);
    for (size_t i = 0; i < sizeof output_counts / sizeof *output_counts; i++) {
        for (int kind = 0; kind < WHOLE_MULTIGATE_COUNT; kind++) {
            for (int effects = 0; effects < 2; effects++) {
                behavior_asset *a = whole_multigate_fixture(output_counts[i], kind, effects != 0);
                char name[64];
                symbol(name, sizeof name, output_counts[i], kind, effects != 0);
                behavior_function_write_c(function(a), out, name);
                behavior_asset_free(a);
            }
        }
    }
    assert(!fclose(out));
}
static double seconds()
{
    struct timespec t;
    assert(!clock_gettime(CLOCK_MONOTONIC, &t));
    return t.tv_sec + t.tv_nsec * 1e-9;
}
/* Handwritten surrounding control flow using the same native state helper.
 * This reference omits graph budgets/inspection, but preserves state/results. */
static void reference(behavior_function_context *ctx, int outputs, int loops)
{
    int flags = ctx->arguments[0].b
                  ? BEHAVIOR_STATE_RESET
                  : (ctx->arguments[1].b ? BEHAVIOR_STATE_LOOP : 0) |
                        (ctx->arguments[2].b ? BEHAVIOR_STATE_RANDOM : 0);
    for (int i = 0; i < loops; i++) {
        int selected = behavior_instance_select_state(
            ctx->value_owner,
            &(behavior_state_selection){ 0, outputs },
            ctx->arguments[3].i,
            flags
        );
        if (selected >= 0) {
            ctx->variables[0].i = selected;
            ctx->variables[1].i++;
        }
    }
}
int bench_multigate(const char *path, int count, int rotation)
{
    void *library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    assert(library && count > 0 && rotation >= 0);
    const int sizes[] = { 1, 5, 17 };
    for (int sample = 0; sample < 12; sample++) {
        int outputs = sizes[sample / 4];
        bool random = sample % 2;
        int kind = sample % 4 >= 2 ? WHOLE_MULTIGATE_LOOP : 0;
        behavior_asset *a = whole_multigate_fixture(outputs, kind, false);
        behavior_function *f = function(a);
        char name[64];
        symbol(name, sizeof name, outputs, kind, false);
        int (*generated)(
            behavior_function_context *
        ) = (int (*)(behavior_function_context *))dlsym(library, name);
        int (*native)(
            behavior_function_context *
        ) = (int (*)(behavior_function_context *))behavior_function_entry(f);
        assert(generated && native);
        uint64_t expected = 0;
        for (int order = 0; order < 4; order++) {
            int mode = (order + rotation) % 4;
            behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
            pin_value args[] = { { .type = PIN_BOOL },
                                 { .type = PIN_BOOL, .b = true },
                                 { .type = PIN_BOOL, .b = random },
                                 { .type = PIN_I32 } };
            behavior_function_activation activation = { 0 };
            behavior_function_activation_init(f, &activation);
            behavior_function_context ctx = { .activation = &activation,
                                              .arguments = args,
                                              .argument_count = 4,
                                              .budget = 100 };
            behavior_instance_bind_values(bi, &ctx);
            behavior_function_prepare_host(f, &ctx);
            srand(32719);
            uint64_t sum = 0, vm = 0, data = 0;
            int strings = 0;
            double start = 0;
            for (int i = -10000; i < count; i++) {
                if (!i) {
                    vm = behavior_function_vm_allocations();
                    data = behavior_function_data_allocations();
                    strings = acount(bi->strings);
                    start = seconds();
                }
                args[0].b = i % 31 == 0;
                args[3].i = (i + 10000) % (outputs + 2) - 1;
                ctx.steps = 0;
                ctx.returned_node_id = 0;
                if (mode == 3) {
                    reference(&ctx, outputs, kind ? 3 : 1);
                } else {
                    int status = mode == 0 ? native(&ctx)
                               : mode == 2 ? generated(&ctx)
                                           : behavior_function_run(f, &ctx, false);
                    assert(!status);
                }
                if (i >= 0) {
                    sum += (unsigned)bi->variables[0].i +
                           (unsigned)bi->variables[1].i;
                }
            }
            double elapsed = seconds() - start;
            assert(
                vm == behavior_function_vm_allocations() &&
                data == behavior_function_data_allocations()
            );
            assert(strings == acount(bi->strings));
            if (!order) {
                expected = sum;
            }
            assert(sum == expected);
            printf(
                "%s multigate_%d ns=%.3f checksum=%llu\n",
                (const char *[]){ "SLJIT", "BYTECODE", "GENERATED_C", "C_DRIVER" }[mode],
                sample,
                elapsed * 1e9 / count,
                (unsigned long long)sum
            );
            behavior_function_release_host(&ctx);
            behavior_function_activation_free(&activation);
            behavior_instance_destroy(bi);
        }
        behavior_asset_free(a);
    }
    assert(!dlclose(library));
    return 0;
}
