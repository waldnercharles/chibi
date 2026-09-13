#include "graph/compiler/behavior_function_internal.h"
#include "graph/document/serialize.h"
#include "graph/model/registry.h"
#include "graph/nodes/state_nodes.h"
#include "graph/runtime/flow_internal.h"
#include "test/flow_snapshot.h"
#include "test/whole_retrigger_fixture.h"

#include <assert.h>
#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    behavior_function *function;
    void *code, *inspection, *dispatch;
} saved_code;

static behavior_function *function(behavior_asset *a)
{
    const behavior_graph_program
        *p = behavior_program_graph(behavior_program_publish(a), a->graph);
    behavior_function *f = (behavior_function *)
        behavior_program_function(p, a->graph->nodes[0]);
    if (!f) {
        const behavior_function_diagnostic
            *d = behavior_program_function_diagnostic(p, a->graph->nodes[0]);
        fprintf(stderr, "Retrigger exclusion: %s\n", d ? d->reason : "missing");
    }
    assert(f);
    return f;
}
static saved_code *replace(behavior_function *f, void *library, int kind)
{
    saved_code *saved = NULL;
    for (int i = 0; i <= acount(f->definitions); i++) {
        behavior_function *part = i ? f->definitions[i - 1] : f;
        apush(saved, ((saved_code){ part, part->code, part->inspection_code, part->dispatch }));
        char name[96];
        snprintf(name, sizeof name, "whole_retrigger_%d", kind);
        if (i) {
            snprintf(name, sizeof name, "whole_retrigger_%d_callee_%d", kind, i);
        }
        part->code = part->inspection_code = dlsym(library, name);
        part->dispatch = NULL;
        assert(part->code);
    }
    return saved;
}
static void restore(saved_code *saved)
{
    for (int i = 0; i < acount(saved); i++) {
        saved[i].function->code = saved[i].code;
        saved[i].function->inspection_code = saved[i].inspection;
        saved[i].function->dispatch = saved[i].dispatch;
    }
    afree(saved);
}
typedef struct
{
    test_flow_snapshot **expected;
    behavior_instance *instance;
    int mode, at, control;
} comparison;
static void observe(comparison *c, flow_run *run, int result)
{
    if (c->mode && flow_run_status(run) != FLOW_IDLE) {
        assert(run->whole_function_executed && !acount(run->children));
    }
    test_flow_snapshot s = test_flow_snapshot_take(run, c->instance, result);
    if (!c->mode) {
        apush(*c->expected, s);
    } else {
        assert(c->at < acount(*c->expected));
        if (acount(s.leaf_traversals) != acount((*c->expected)[c->at].leaf_traversals)) {
            fprintf(
                stderr,
                "retrigger snapshot mode=%d at=%d state=%d steps=%lu leaf "
                "traversals=%d expected=%d root=%d\n",
                c->mode,
                c->at,
                s.state,
                s.steps,
                acount(s.leaf_traversals),
                acount((*c->expected)[c->at].leaf_traversals),
                s.leaf_root
            );
        }
        test_flow_snapshot_same(s, (*c->expected)[c->at]);
        test_flow_snapshot_free(&s);
    }
    c->at++;
    assert(c->at < 2000);
}
static int advance(comparison *c, flow_run *run, int result)
{
    for (;;) {
        observe(c, run, result);
        if (result != FLOW_PAUSED_RESULT) {
            return result;
        }
        result = c->control == 2 ? flow_run_step(run) : flow_run_resume(run);
    }
}
static flow_run *start(comparison *c, double duration, int value, unsigned budget)
{
    pin_value args[] = { { .type = PIN_F64, .d = duration },
                         { .type = PIN_I32, .i = value },
                         { .type = PIN_I32, .i = 3 } };
    flow_run *run = behavior_instance_enqueue(
        c->instance,
        c->instance->asset->graph->nodes[0],
        (flow_inputs){ args, 3 },
        NULL
    );
    assert(run);
    run->step_budget = budget;
    advance(c, run, c->control == 2 ? flow_run_step(run) : flow_run_resume(run));
    return run;
}
static void parity(int kind, int control, double duration, unsigned budget, void *library)
{
    if (getenv("CBLUEPRINT_TEST_RETRIGGER_CASES")) {
        fprintf(stderr, "retrigger kind=%d control=%d duration=%g budget=%u\n", kind, control, duration, budget);
    }
    behavior_asset *a = whole_retrigger_fixture(kind);
    if (control == 5) {
        for (int g = 0; g < acount(a->graphs); g++) {
            for (int n = 0; n < acount(a->graphs[g]->nodes); n++) {
                node *at = a->graphs[g]->nodes[n];
                if (at->desc == &NODE_SetVariable &&
                    at->reference_id == a->variables[0].id) {
                    assert(!graph_set_breakpoint(a->graphs[g], at, true));
                }
            }
        }
    }
    behavior_function *f = function(a);
    test_flow_snapshot *expected = NULL;
    for (int mode = 0; mode < (library ? 4 : 3); mode++) {
        saved_code *saved = mode == 3 ? replace(f, library, kind) : NULL;
        behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
        assert(!behavior_instance_set_backend(
            bi,
            (
                behavior_backend[]
            ){ BEHAVIOR_ORACLE, BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT, BEHAVIOR_SLJIT }[mode]
        ));
        behavior_instance_set_trace_enabled(bi, control != 0);
        behavior_instance_set_breakpoints_enabled(bi, control == 5);
        comparison c = { &expected, bi, mode, 0, control };
        uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
        flow_run *first = start(&c, 1., 7, budget);
        if (control == 3 && flow_run_status(first) == FLOW_SUSPENDED) {
            assert(!flow_run_pause(first));
        }
        bi->current_time = .5;
        flow_run *second = start(&c, duration, 42, budget);
        if (budget == 1000) {
            assert(flow_run_status(first) == FLOW_SUSPENDED);
            assert(flow_run_status(second) == FLOW_COMPLETED);
            double wake = flow_run_wake_time(first);
            assert(isnan(duration) ? isnan(wake) : wake == .5 + duration);
            assert(bi->variables[0].i == 0 && bi->variables[1].i == 1);
        }
        if (flow_run_status(first) == FLOW_SUSPENDED) {
            observe(&c, first, FLOW_SUSPENDED_RESULT);
        }
        if (control == 4) {
            assert(!flow_run_stop(first));
            observe(&c, first, FLOW_OK);
        }
        bi->current_time = .75;
        flow_run *third = start(&c, .25, 99, budget);
        flow_run *runs[] = { first, second, third };
        for (int i = 0; i < 3; i++) {
            int waits = 0;
            while (flow_run_status(runs[i]) == FLOW_SUSPENDED) {
                double wake = flow_run_wake_time(runs[i]);
                assert(isfinite(wake));
                advance(&c, runs[i], flow_run_tick_at(runs[i], fmax(.75, wake)));
                assert(++waits < 8);
            }
        }
        if (budget == 1000) {
            assert(bi->variables[0].i == (control == 4 ? 99 : 7));
            assert(bi->variables[1].i == (control == 4 ? 2 : 3));
        }
        if (mode) {
            assert(c.at == acount(expected));
            assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
        }
        behavior_instance_destroy(bi);
        restore(saved);
    }
    for (int i = 0; i < acount(expected); i++) {
        test_flow_snapshot_free(&expected[i]);
    }
    afree(expected);
    behavior_asset_free(a);
}
static void missing_context()
{
    behavior_asset *a = whole_retrigger_fixture(WHOLE_RETRIGGER_DIRECT);
    behavior_function *f = function(a);
    for (int mode = 0; mode < 2; mode++) {
        behavior_function_activation activation = { 0 };
        behavior_function_activation_init(f, &activation);
        pin_value args[] = { { .type = PIN_F64, .d = 1 },
                             { .type = PIN_I32, .i = 7 },
                             { .type = PIN_I32 } };
        behavior_function_context ctx = { .activation = &activation,
                                          .arguments = args,
                                          .argument_count = 3,
                                          .budget = 1000 };
        assert(behavior_function_run(f, &ctx, mode != 0) == FLOW_ERR_STATE);
        assert(ctx.error_pin == -1 && ctx.node_id);
        node *n = graph_node_by_id(a->graph, ctx.node_id);
        assert(n && n->desc == &NODE_RetriggerableDelay);
        assert(!strcmp(ctx.error_message, "Retriggerable Delay requires an instance"));
        behavior_function_activation_free(&activation);
    }
    behavior_asset_free(a);
}
static flow_run *dispatch(behavior_instance *bi, double duration, int value)
{
    pin_value args[] = { { .type = PIN_F64, .d = duration },
                         { .type = PIN_I32, .i = value },
                         { .type = PIN_I32, .i = 3 } };
    flow_run *run = behavior_instance_dispatch(bi, "Run", (flow_inputs){ args, 3 }, NULL);
    assert(run);
    return run;
}
static void clear(behavior_instance *bi)
{
    while (acount(bi->activations)) {
        flow_run *run = bi->activations[0];
        assert(!flow_run_stop(run));
        assert(!behavior_instance_release_activation(bi, run));
    }
}
static void lifecycle(int kind, behavior_backend backend)
{
    behavior_asset *a = whole_retrigger_fixture(kind);
    char *json = behavior_asset_save_json(a);
    behavior_asset *next = behavior_asset_load_json(json);
    free(json);
    assert(next);
    for (int j = 0; j < acount(next->graphs); j++) {
        graph *g = next->graphs[j];
        for (int n = 1; n < acount(g->nodes); n++) {
            if (g->nodes[n]->desc == &NODE_RetriggerableDelay) {
                node *delay = g->nodes[n];
                int last = acount(g->nodes) - 1;
                g->nodes[n] = g->nodes[last];
                g->nodes[last] = delay;
                break;
            }
        }
    }
    behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
    behavior_instance *other = behavior_instance_create(a, NULL, NULL, NULL);
    assert(!behavior_instance_set_backend(bi, backend));
    assert(!behavior_instance_set_backend(other, backend));
    behavior_instance_set_trace_enabled(bi, false);
    behavior_instance_set_trace_enabled(other, false);
    assert(!behavior_instance_set_activation_pool(bi, true));
    uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
    flow_run *first = dispatch(bi, 1, 7), *independent = dispatch(other, 2, 99);
    assert(flow_run_status(first) == FLOW_SUSPENDED);
    assert(behavior_instance_can_update(bi, next) == INSTANCE_UPDATE_BUSY);
    bi->current_time = .25;
    flow_run *second = dispatch(bi, .5, 42);
    assert(flow_run_status(second) == FLOW_COMPLETED);
    assert(flow_run_wake_time(first) == .75 && flow_run_wake_time(independent) == 2.);
    clear(bi);
    behavior_instance_destroy(other);
    assert(behavior_instance_can_update(bi, next) == INSTANCE_UPDATE_READY);
    behavior_instance_update(bi, next);
    behavior_asset_free(a);
    a = next;
    first = dispatch(bi, 1, 7);
    second = dispatch(bi, .5, 42);
    assert(flow_run_status(first) == FLOW_SUSPENDED && flow_run_status(second) == FLOW_COMPLETED);
    assert(flow_run_wake_time(first) == bi->current_time + .5);
    assert(backend == BEHAVIOR_ORACLE || first->whole_function_executed);
    clear(bi);
    assert(!behavior_instance_pool_release(bi));
    assert(behavior_instance_pool_reuse(bi, a, NULL, NULL, NULL) == bi);
    assert(!behavior_instance_set_backend(bi, backend));
    first = dispatch(bi, 1, 7);
    assert(flow_run_status(first) == FLOW_SUSPENDED);
    assert(bi->variables[0].i == 0 && bi->variables[1].i == 0);
    assert(flow_run_wake_time(first) == bi->current_time + 1.);
    behavior_instance_destroy(bi);
    behavior_asset_free(a);
    if (backend != BEHAVIOR_ORACLE) {
        assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
    }
}
static void paused(behavior_backend backend)
{
    behavior_asset *a = whole_retrigger_fixture(WHOLE_RETRIGGER_DIRECT);
    for (int n = 0; n < acount(a->graph->nodes); n++) {
        node *at = a->graph->nodes[n];
        if (at->desc == &NODE_RetriggerableDelay) {
            assert(!graph_set_breakpoint(a->graph, at, true));
        }
    }
    behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
    assert(!behavior_instance_set_backend(bi, backend));
    behavior_instance_set_breakpoints_enabled(bi, true);
    uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
    flow_run *first = dispatch(bi, 1, 7);
    assert(flow_run_status(first) == FLOW_PAUSED);
    behavior_instance_set_breakpoints_enabled(bi, false);
    flow_run *second = dispatch(bi, 1, 42);
    assert(flow_run_status(first) == FLOW_PAUSED && flow_run_status(second) == FLOW_SUSPENDED);
    assert(flow_run_wake_time(second) == 1.);
    assert(!flow_run_tick_at(first, .5));
    assert(flow_run_status(first) == FLOW_COMPLETED);
    assert(flow_run_wake_time(second) == 1.5);
    assert(!flow_run_tick_at(second, 1.5));
    assert(bi->variables[0].i == 42 && bi->variables[1].i == 2);
    behavior_instance_destroy(bi);
    behavior_asset_free(a);
    if (backend != BEHAVIOR_ORACLE) {
        assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
    }
}
void test_compiled_retrigger(const char *path)
{
    whole_retrigger_smoke();
    void *library = path ? dlopen(path, RTLD_NOW | RTLD_LOCAL) : NULL;
    assert(!path || library);
    const double durations[] = { -.125, 0., 1., INFINITY, NAN };
    const unsigned budgets[] = { 1, 8, 1000 };
    for (int kind = 0; kind < WHOLE_RETRIGGER_COUNT; kind++) {
        for (int control = 0; control < 6; control++) {
            for (int d = 0; d < 5; d++) {
                for (int b = 0; b < 3; b++) {
                    parity(kind, control, durations[d], budgets[b], library);
                }
            }
        }
    }
    missing_context();
    for (int mode = 0; mode < 3; mode++) {
        behavior_backend backend = (behavior_backend[]){ BEHAVIOR_ORACLE,
                                                         BEHAVIOR_BYTECODE,
                                                         BEHAVIOR_SLJIT }[mode];
        paused(backend);
        for (int kind = 0; kind < WHOLE_RETRIGGER_COUNT; kind++) {
            lifecycle(kind, backend);
        }
    }
    if (library) {
        assert(!dlclose(library));
    }
    printf(
        "PASS: Retriggerable Delay oracle/bytecode/SLJIT%s deadline, input "
        "retention, Sequence tails, recursive waits, cancellation, budget and "
        "debugger snapshot parity\n",
        path ? "/generated-C" : ""
    );
}
void emit_retrigger_c(const char *path)
{
    FILE *out = fopen(path, "w");
    assert(out);
    for (int kind = 0; kind < WHOLE_RETRIGGER_COUNT; kind++) {
        behavior_asset *a = whole_retrigger_fixture(kind);
        char name[64];
        snprintf(name, sizeof name, "whole_retrigger_%d", kind);
        behavior_function_write_c(function(a), out, name);
        behavior_asset_free(a);
    }
    assert(!fclose(out));
}
